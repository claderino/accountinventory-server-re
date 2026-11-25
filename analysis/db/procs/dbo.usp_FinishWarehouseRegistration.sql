-- ===========================================================
-- Title		:	usp_FinishWarehouseRegistration
-- Author		:	ÀÌÃ¢¿ë / Chang-yong Lee (sibukano@ncsoft.com)
-- Create date	:	2013-07-12
-- Description	:	
-- ===========================================================
CREATE PROCEDURE dbo.usp_FinishWarehouseRegistration
(
	@Result				BIT,
	@LabelID			BIGINT,
	@OwnerAccountID		UNIQUEIDENTIFIER,
	@GoodsID			BIGINT
)
AS
BEGIN
	SET NOCOUNT, XACT_ABORT ON;
	
	DECLARE	@INIT_TRANCOUNT		INT	= @@TRANCOUNT,
			-- Error Code
			@ERR_SUCCESS		INT = 0,
			@ERR_FAILURE		INT = 1;
	
	BEGIN TRY
		-- Transaction validity check
		IF	(@@NESTLEVEL = 1 AND @INIT_TRANCOUNT > 0)
			RAISERROR(N'invalid initial transaction, init trancount:%d', 16, @ERR_FAILURE, @INIT_TRANCOUNT);
		
		DECLARE	@resultStr			NCHAR(1)	= CAST(@Result AS NCHAR(1)),
				@ownerAccountIdStr	NCHAR(36)	= CAST(@OwnerAccountID AS NCHAR(36));
		-- Parameter validity check
		IF	(	@Result	IS NULL
				OR	ISNULL(@LabelID, -1) <= 0
				OR	@OwnerAccountID IS NULL
				OR	@GoodsID IS NULL	)
			RAISERROR(N'invalid parameter, result:%s, labelId:0x%I64X, ownerAccountId:%s, goodsId:0x%I64X', 16, @ERR_FAILURE, @resultStr, @LabelID, @ownerAccountIdStr, @GoodsID);
		
		IF	(@INIT_TRANCOUNT = 0)
			BEGIN TRAN
		
		BEGIN
			DECLARE	@itemCount		TINYINT		= 0,
					@resultCount	INT			= 0,
					@currentTime	DATETIME	= GETDATE();
			
			UPDATE	dbo.WarehouseGoods
			SET		@itemCount				=	RemainingItemCount,
					RegistrationState		=	CASE	@Result
														WHEN	1	THEN	2	-- Registration Success
														ELSE				3	-- Registration Failure
												END,
					LastStateUpdateTime		=	@currentTime
			WHERE	LabelID					=	@LabelID
			AND		OwnerAccountID			=	@OwnerAccountID
			AND		GoodsID					=	@GoodsID
			AND		RegistrationState		=	1;	-- Registration Ready
			IF	(@@ROWCOUNT != 1)
				RAISERROR(N'there is no such warehouse goods, result:%s, labelId:0x%I64X, ownerAccountId:%s, goodsId:0x%I64X', 16, @ERR_FAILURE, @resultStr, @LabelID, @ownerAccountIdStr, @GoodsID);
			
			IF	(@Result = 1)	BEGIN	-- Success
				UPDATE	dbo.WarehouseItem
				SET		ItemState			= 1,	-- Usable
						LastStateUpdateTime	= @currentTime
				WHERE	LabelID				= @LabelID
				AND		ItemState			= 0;	-- Invalid State
				SET	@resultCount = @@ROWCOUNT;
				IF	(@itemCount != @resultCount)
					RAISERROR(N'failed to update item state; result count is not same, labelId:0x%I64X, ownerAccountId:%s, goodsId:0x%I64X, itemCount:%d, resultCount:%d', 16, @ERR_FAILURE,
																						@LabelID, @ownerAccountIdStr, @GoodsID, @itemCount, @resultCount);
				
				DECLARE	@ret	INT	= 0;
				EXEC	@ret	= usp_IncreaseWarehouseItemCount	@OwnerAccountID, @LabelID, @itemCount;
				IF	(@ret != @ERR_SUCCESS)
					RAISERROR(N'failed to increase warehouse item count, ownerAccountId:%s, labelId:0x%I64X, itemCount:%d', 16, @ERR_FAILURE, @ownerAccountIdStr, @LabelID, @itemCount);
				
				SELECT	Goods.LabelID,
						Goods.GoodsNumber,
						Goods.SenderDescription,
						Goods.SenderMessage,
						Goods.PurchaseTime,
						Goods.RegistrationTime,
						Item.ItemInstanceID,
						Item.GoodsItemNumber,
						Item.ItemDataID,
						Item.ItemAmount,
						Item.UsableDuration,
						Item.ItemState
				FROM	(
							SELECT	LabelID,
									GoodsNumber,
									SenderDescription,
									SenderMessage,
									PurchaseTime,
									RegistrationTime
							FROM	dbo.WarehouseGoods
							WHERE	LabelID	= @LabelID
						)	Goods
						INNER LOOP JOIN
						(
							SELECT	ItemInstanceID,
									LabelID,
									GoodsItemNumber,
									ItemDataID,
									ItemAmount,
									UsableDuration,
									ItemState
							FROM	dbo.WarehouseItem
							WITH	(FORCESEEK)
						)	Item
						ON	Item.LabelID	= Goods.LabelID;
			END
		END
		
		IF	(@INIT_TRANCOUNT = 0)
			COMMIT;
		
		RETURN	@ERR_SUCCESS;
	END TRY
	BEGIN CATCH
		IF	(XACT_STATE() != 0 AND @INIT_TRANCOUNT = 0)	BEGIN
			ROLLBACK;
			EXEC dbo.usp_LogError;
		END
		
		DECLARE	@ERR_NUMBER		INT				= ERROR_NUMBER(),
				@ERR_SEVERITY	INT				= ERROR_SEVERITY(),
				@ERR_STATE		INT				= ERROR_STATE(),
				@ERR_PROCEDURE	NVARCHAR(126)	= ERROR_PROCEDURE(),
				@ERR_MESSAGE	NVARCHAR(2048)	= ERROR_MESSAGE(),
				@ERR_CODE		INT				= @ERR_FAILURE;
		
		IF	(@ERR_NUMBER >= 50000)	BEGIN
			IF	(@ERR_PROCEDURE = OBJECT_NAME(@@PROCID))
				SET	@ERR_CODE = @ERR_STATE;
		END
		
		IF	(@@NESTLEVEL = 1)
			RAISERROR(N'[%s], %s', @ERR_SEVERITY, @ERR_CODE, @ERR_PROCEDURE, @ERR_MESSAGE);
		ELSE
			RAISERROR(@ERR_MESSAGE, @ERR_SEVERITY, @ERR_CODE);
		
		RETURN	@ERR_CODE;
	END CATCH
END


(1 rows affected)
