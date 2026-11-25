-- ===========================================================
-- Title		:	usp_FinishWarehouseItemConsumption
-- Author		:	ÀÌÃ¢¿ë / Chang-yong Lee (sibukano@ncsoft.com)
-- Create date	:	2013-07-12
-- Description	:	
-- ===========================================================
CREATE PROCEDURE dbo.usp_FinishWarehouseItemConsumption
(
	@Result				BIT,
	@LabelID			BIGINT,
	@ItemInstanceID		BIGINT,
	@OwnerAccountID		UNIQUEIDENTIFIER
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
		IF	(	@Result IS NULL
				OR	ISNULL(@LabelID, -1) <= 0
				OR	ISNULL(@ItemInstanceID, -1) <= 0
				OR	@OwnerAccountID IS NULL		)
			RAISERROR(N'invalid parameter, result:%s, labelId:0x%I64X, itemInstanceId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE, @resultStr, @LabelID, @ItemInstanceID, @ownerAccountIdStr);
		
		IF	(@INIT_TRANCOUNT = 0)
			BEGIN TRAN
		
		BEGIN
			IF	(@Result = 1)	BEGIN	-- Success
				DECLARE	@ret				INT		= 0,
						@remainingItemCount	TINYINT	= 0;
				
				UPDATE	dbo.WarehouseGoods
				SET		@remainingItemCount	= RemainingItemCount -= 1
				WHERE	LabelID				= @LabelID
				AND		OwnerAccountID		= @OwnerAccountID
				AND		RegistrationState	= 2;	--	Registration Success
				IF	(@@ROWCOUNT != 1)
					RAISERROR(N'failed to update remaining item count; there is no such goods or invalid registration state, result:%s, labelId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE,
																															@resultStr, @LabelID, @ownerAccountIdStr);
				
				UPDATE	dbo.WarehouseItem
				SET		ItemState			= 3,	--	Exportation Success
						LastStateUpdateTime	= GETDATE()
				WHERE	ItemInstanceID		= @ItemInstanceID
				AND		LabelID				= @LabelID
				AND		OwnerAccountID		= @OwnerAccountID
				AND		ItemState			= 2		-- Exportation Try
				AND		UserServiceType		= 2;	-- Lobby Service
				IF	(@@ROWCOUNT != 1)
					RAISERROR(N'failed to finish warehouse item consumption; there is no such item or invalid item state, result:%s, labelId:0x%I64X, itemInstanceId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE,
																														@resultStr, @LabelID, @ItemInstanceID, @ownerAccountIdStr);
				
				IF	(@remainingItemCount = 0)	BEGIN
					EXEC	@ret	= dbo.usp_ArchiveDisusedWarehouseData	@LabelID;
					IF	(@ret != @ERR_SUCCESS)
						RAISERROR(N'failed to archive disused warehouse data, labelId:0x%I64X', 16, @ERR_FAILURE, @LabelID);
				END
				
				EXEC	@ret	= dbo.usp_DecreaseWarehouseItemCount	@OwnerAccountID, @remainingItemCount;
				IF	(@ret != @ERR_SUCCESS)
					RAISERROR(N'failed to decrease warehouse item count, ownerAccountId:%s, remainingItemCount:%d', 16, @ERR_FAILURE, @ownerAccountIdStr, @remainingItemCount);
			END
			ELSE	BEGIN	-- Failure
				UPDATE	dbo.WarehouseItem
				SET		ItemState			= 4,	-- Exportation Failure
						LastStateUpdateTime	= GETDATE()
				WHERE	ItemInstanceID		= @ItemInstanceID
				AND		LabelID				= @LabelID
				AND		OwnerAccountID		= @OwnerAccountID
				AND		ItemState			= 2		-- Exportation Try
				AND		UserServiceType		= 2;	-- Lobby Service
				IF	(@@ROWCOUNT != 1)
					RAISERROR(N'failed to finish warehouse item consumption; there is no such item or invalid item state, result:%s, labelId:0x%I64X, itemInstanceId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE,
																														@resultStr, @LabelID, @ItemInstanceID, @ownerAccountIdStr);
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
