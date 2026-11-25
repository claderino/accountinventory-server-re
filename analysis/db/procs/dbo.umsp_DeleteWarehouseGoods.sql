-- ===========================================================
-- Title		:	umsp_DeleteWarehouseGoods
-- Author		:	ÀÌÃ¢¿ë / Chang-yong Lee (sibukano@ncsoft.com)
-- Create date	:	2014-01-24
-- Description	:	
-- ===========================================================
CREATE PROCEDURE dbo.umsp_DeleteWarehouseGoods
(
	@LabelID			BIGINT,
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
		
		DECLARE	@ownerAccountIdStr	NCHAR(36)	= CAST(@OwnerAccountID AS NCHAR(36));
		-- Parameter validity check
		IF	(	ISNULL(@LabelID, -1) <= 0
				OR	@OwnerAccountID IS NULL	)
			RAISERROR(N'invalid parameter, labelId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE, @LabelID, @ownerAccountIdStr);
		
		IF	(@INIT_TRANCOUNT = 0)
			BEGIN TRAN
		
		BEGIN
			DECLARE	@ret				INT		= 0,
					@deletedItemCount	TINYINT	= 0;
			
			UPDATE	dbo.WarehouseGoods
			SET		@deletedItemCount	= RemainingItemCount,
					RemainingItemCount	= 0
			WHERE	LabelID				= @LabelID
			AND		OwnerAccountID		= @OwnerAccountID
			AND		RegistrationState	= 2;	--	Registration Success
			IF	(@@ROWCOUNT != 1)
				RAISERROR(N'failed to update remaining item count; there is no such goods or invalid registration state, labelId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE,
																														@LabelID, @ownerAccountIdStr);
			
			UPDATE	dbo.WarehouseItem
			SET		ItemState			= 5,		--	Forced Deletion
					LastStateUpdateTime	= GETDATE()
			WHERE	LabelID				= @LabelID
			AND		ItemState			IN (1, 4);	--	Usable, Exportation Failure
			IF	(@@ROWCOUNT != @deletedItemCount)
				RAISERROR(N'cannot delete warehouse item; there is no such warehouse item or there is uncompleted warehouse item transaction, labelId:0x%I64X, ownerAccountId:%s', 16, @ERR_FAILURE,
																																				@LabelID, @ownerAccountIdStr);
			
			EXEC	@ret	= dbo.usp_ArchiveDisusedWarehouseData	@LabelID;
			IF	(@ret != @ERR_SUCCESS)
				RAISERROR(N'failed to archive disused warehouse data, labelId:0x%I64X', 16, @ERR_FAILURE, @LabelID);
			
			UPDATE	dbo.WarehouseStatistics
			SET		WarehouseGoodsCount	-=	1,
					WarehouseItemCount	-=	@deletedItemCount
			WHERE	OwnerAccountID		=	@OwnerAccountID;
			IF	(@@ROWCOUNT != 1)
				RAISERROR(N'failed to decrease warehouse item count, ownerAccountId:%s, deletedItemCount:%d', 16, @ERR_FAILURE, @ownerAccountIdStr, @deletedItemCount);
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
